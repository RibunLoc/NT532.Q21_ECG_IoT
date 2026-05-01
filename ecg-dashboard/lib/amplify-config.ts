import { Amplify } from 'aws-amplify';

const REGION = 'ap-southeast-1';

Amplify.configure({
  Auth: {
    Cognito: {
      userPoolId:          'ap-southeast-1_HcxI5inaJ',
      userPoolClientId:    '1grvnkaf5vh2nmfbflke58007d',
      identityPoolId:      'ap-southeast-1:02c14e45-bf07-4784-aa25-dd603f058dd5',
      loginWith: { email: true },
    },
  },
});

export const IOT_ENDPOINT = 'a2blv28aii5w0c-ats.iot.ap-southeast-1.amazonaws.com';
export const AWS_REGION   = REGION;
export const DDB_TABLE    = 'ecg-events';
